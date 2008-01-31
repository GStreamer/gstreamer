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
#ifdef __sun
/* Needed on older Solaris Nevada builds (72 at least) */
#include <stropts.h>
#include <sys/ioccom.h>
#endif

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


#define GST_TYPE_V4L2_BUFFER (gst_v4l2_buffer_get_type())
#define GST_IS_V4L2_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_V4L2_BUFFER))
#define GST_V4L2_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_V4L2_BUFFER, GstV4l2Buffer))


/* Local functions */
static gboolean
gst_v4l2src_get_nearest_size (GstV4l2Src * v4l2src, guint32 pixelformat,
    gint * width, gint * height);

static void
gst_v4l2_buffer_finalize (GstV4l2Buffer * buffer)
{
  GstV4l2BufferPool *pool;
  gboolean resuscitated = FALSE;
  gint index;

  pool = buffer->pool;

  index = buffer->vbuffer.index;

  GST_LOG ("finalizing buffer %p %d", buffer, index);

  g_mutex_lock (pool->lock);
  if (GST_BUFFER_SIZE (buffer) != 0)
    /* BUFFER_SIZE is only set if the frame was dequeued */
    pool->num_live_buffers--;

  if (pool->running) {
    if (ioctl (pool->video_fd, VIDIOC_QBUF, &buffer->vbuffer) < 0) {
      GST_WARNING ("could not requeue buffer %p %d", buffer, index);
    } else {
      /* FIXME: check that the caps didn't change */
      GST_LOG ("reviving buffer %p, %d", buffer, index);
      gst_buffer_ref (GST_BUFFER (buffer));
      GST_BUFFER_SIZE (buffer) = 0;
      pool->buffers[index] = buffer;
      resuscitated = TRUE;
    }
  } else {
    GST_LOG ("the pool is shutting down");
  }
  g_mutex_unlock (pool->lock);

  if (!resuscitated) {
    GST_LOG ("buffer %p not recovered, unmapping", buffer);
    gst_mini_object_unref (GST_MINI_OBJECT (pool));
    munmap ((void *) GST_BUFFER_DATA (buffer), buffer->vbuffer.length);
  }
}

static void
gst_v4l2_buffer_init (GstV4l2Buffer * xvimage, gpointer g_class)
{
  /* NOP */
}

static void
gst_v4l2_buffer_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_v4l2_buffer_finalize;
}

static GType
gst_v4l2_buffer_get_type (void)
{
  static GType _gst_v4l2_buffer_type;

  if (G_UNLIKELY (_gst_v4l2_buffer_type == 0)) {
    static const GTypeInfo v4l2_buffer_info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_v4l2_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstV4l2Buffer),
      0,
      (GInstanceInitFunc) gst_v4l2_buffer_init,
      NULL
    };
    _gst_v4l2_buffer_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstV4l2Buffer", &v4l2_buffer_info, 0);
  }
  return _gst_v4l2_buffer_type;
}

static GstV4l2Buffer *
gst_v4l2_buffer_new (GstV4l2BufferPool * pool, guint index, GstCaps * caps)
{
  GstV4l2Buffer *ret;
  guint8 *data;

  ret = (GstV4l2Buffer *) gst_mini_object_new (GST_TYPE_V4L2_BUFFER);

  GST_LOG ("creating buffer %u, %p in pool %p", index, ret, pool);

  ret->pool = pool;
  gst_mini_object_ref (GST_MINI_OBJECT (pool));
  memset (&ret->vbuffer, 0x00, sizeof (ret->vbuffer));

  ret->vbuffer.index = index;
  ret->vbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  ret->vbuffer.memory = V4L2_MEMORY_MMAP;

  if (ioctl (pool->video_fd, VIDIOC_QUERYBUF, &ret->vbuffer) < 0)
    goto querybuf_failed;

  GST_LOG ("  index:     %u", ret->vbuffer.index);
  GST_LOG ("  type:      %d", ret->vbuffer.type);
  GST_LOG ("  bytesused: %u", ret->vbuffer.bytesused);
  GST_LOG ("  flags:     %08x", ret->vbuffer.flags);
  GST_LOG ("  field:     %d", ret->vbuffer.field);
  GST_LOG ("  memory:    %d", ret->vbuffer.memory);
  if (ret->vbuffer.memory == V4L2_MEMORY_MMAP)
    GST_LOG ("  MMAP offset:  %u", ret->vbuffer.m.offset);
  GST_LOG ("  length:    %u", ret->vbuffer.length);
  GST_LOG ("  input:     %u", ret->vbuffer.input);

  data = (guint8 *) mmap (0, ret->vbuffer.length,
      PROT_READ | PROT_WRITE, MAP_SHARED, pool->video_fd,
      ret->vbuffer.m.offset);

  if (data == MAP_FAILED)
    goto mmap_failed;

  GST_BUFFER_DATA (ret) = data;
  GST_BUFFER_SIZE (ret) = ret->vbuffer.length;

  GST_BUFFER_FLAG_SET (ret, GST_BUFFER_FLAG_READONLY);

  gst_buffer_set_caps (GST_BUFFER (ret), caps);

  return ret;

  /* ERRORS */
querybuf_failed:
  {
    gint errnosave = errno;

    GST_WARNING ("Failed QUERYBUF: %s", g_strerror (errnosave));
    gst_buffer_unref (GST_BUFFER (ret));
    errno = errnosave;
    return NULL;
  }
mmap_failed:
  {
    gint errnosave = errno;

    GST_WARNING ("Failed to mmap: %s", g_strerror (errnosave));
    gst_buffer_unref (GST_BUFFER (ret));
    errno = errnosave;
    return NULL;
  }
}

#define GST_TYPE_V4L2_BUFFER_POOL (gst_v4l2_buffer_pool_get_type())
#define GST_IS_V4L2_BUFFER_POOL(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_V4L2_BUFFER_POOL))
#define GST_V4L2_BUFFER_POOL(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_V4L2_BUFFER_POOL, GstV4l2BufferPool))

static void
gst_v4l2_buffer_pool_finalize (GstV4l2BufferPool * pool)
{
  g_mutex_free (pool->lock);
  pool->lock = NULL;

  if (pool->video_fd >= 0)
    close (pool->video_fd);

  if (pool->buffers)
    g_free (pool->buffers);
  pool->buffers = NULL;
}

static void
gst_v4l2_buffer_pool_init (GstV4l2BufferPool * pool, gpointer g_class)
{
  pool->lock = g_mutex_new ();
  pool->running = FALSE;
  pool->num_live_buffers = 0;
}

static void
gst_v4l2_buffer_pool_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_v4l2_buffer_pool_finalize;
}

static GType
gst_v4l2_buffer_pool_get_type (void)
{
  static GType _gst_v4l2_buffer_pool_type;

  if (G_UNLIKELY (_gst_v4l2_buffer_pool_type == 0)) {
    static const GTypeInfo v4l2_buffer_pool_info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_v4l2_buffer_pool_class_init,
      NULL,
      NULL,
      sizeof (GstV4l2BufferPool),
      0,
      (GInstanceInitFunc) gst_v4l2_buffer_pool_init,
      NULL
    };
    _gst_v4l2_buffer_pool_type = g_type_register_static (GST_TYPE_MINI_OBJECT,
        "GstV4l2BufferPool", &v4l2_buffer_pool_info, 0);
  }
  return _gst_v4l2_buffer_pool_type;
}

static GstV4l2BufferPool *
gst_v4l2_buffer_pool_new (GstV4l2Src * v4l2src, gint fd, gint num_buffers,
    GstCaps * caps)
{
  GstV4l2BufferPool *pool;
  gint n;

  pool = (GstV4l2BufferPool *) gst_mini_object_new (GST_TYPE_V4L2_BUFFER_POOL);

  pool->video_fd = dup (fd);
  if (pool->video_fd < 0)
    goto dup_failed;

  pool->buffer_count = num_buffers;
  pool->buffers = g_new0 (GstV4l2Buffer *, num_buffers);

  for (n = 0; n < num_buffers; n++) {
    pool->buffers[n] = gst_v4l2_buffer_new (pool, n, caps);
    if (!pool->buffers[n])
      goto buffer_new_failed;
  }

  return pool;

  /* ERRORS */
dup_failed:
  {
    gint errnosave = errno;

    gst_mini_object_unref (GST_MINI_OBJECT (pool));

    errno = errnosave;

    return NULL;
  }
buffer_new_failed:
  {
    gint errnosave = errno;

    gst_mini_object_unref (GST_MINI_OBJECT (pool));

    errno = errnosave;

    return NULL;
  }
}

static gboolean
gst_v4l2_buffer_pool_activate (GstV4l2BufferPool * pool, GstV4l2Src * v4l2src)
{
  gint n;

  g_mutex_lock (pool->lock);

  for (n = 0; n < pool->buffer_count; n++) {
    struct v4l2_buffer *buf;

    buf = &pool->buffers[n]->vbuffer;

    GST_LOG ("enqueue pool buffer %d", n);

    if (ioctl (pool->video_fd, VIDIOC_QBUF, buf) < 0)
      goto queue_failed;
  }
  pool->running = TRUE;

  g_mutex_unlock (pool->lock);

  return TRUE;

  /* ERRORS */
queue_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, READ,
        (_("Could not enqueue buffers in device '%s'."),
            v4l2src->v4l2object->videodev),
        ("enqueing buffer %d/%d failed: %s",
            n, v4l2src->num_buffers, g_strerror (errno)));
    g_mutex_unlock (pool->lock);
    return FALSE;
  }
}

static void
gst_v4l2_buffer_pool_destroy (GstV4l2BufferPool * pool)
{
  gint n;

  g_mutex_lock (pool->lock);
  pool->running = FALSE;
  g_mutex_unlock (pool->lock);

  /* after this point, no more buffers will be queued or dequeued; no buffer
   * from pool->buffers that is NULL will be set to a buffer, and no buffer that
   * is not NULL will be pushed out. */

  for (n = 0; n < pool->buffer_count; n++) {
    GstBuffer *buf;

    g_mutex_lock (pool->lock);
    buf = GST_BUFFER (pool->buffers[n]);
    g_mutex_unlock (pool->lock);

    if (buf)
      /* we own the ref if the buffer is in pool->buffers; drop it. */
      gst_buffer_unref (buf);
  }

  gst_mini_object_unref (GST_MINI_OBJECT (pool));
}

/* complete made up ranking, the values themselves are meaningless */
#define YUV_BASE_RANK     1000
#define JPEG_BASE_RANK     500
#define DV_BASE_RANK       200
#define RGB_BASE_RANK      100
#define YUV_ODD_BASE_RANK   50
#define RGB_ODD_BASE_RANK   25
#define BAYER_BASE_RANK     15
#define GREY_BASE_RANK       5

static gint
gst_v4l2src_format_get_rank (guint32 fourcc)
{
  switch (fourcc) {
    case V4L2_PIX_FMT_MJPEG:
      return JPEG_BASE_RANK;
    case V4L2_PIX_FMT_JPEG:
      return JPEG_BASE_RANK + 1;

    case V4L2_PIX_FMT_RGB332:
    case V4L2_PIX_FMT_RGB555:
    case V4L2_PIX_FMT_RGB555X:
    case V4L2_PIX_FMT_RGB565:
    case V4L2_PIX_FMT_RGB565X:
      return RGB_ODD_BASE_RANK;

    case V4L2_PIX_FMT_RGB24:
    case V4L2_PIX_FMT_BGR24:
      return RGB_BASE_RANK - 1;

    case V4L2_PIX_FMT_RGB32:
    case V4L2_PIX_FMT_BGR32:
      return RGB_BASE_RANK;

    case V4L2_PIX_FMT_GREY:    /*  8  Greyscale     */
      return GREY_BASE_RANK;

    case V4L2_PIX_FMT_NV12:    /* 12  Y/CbCr 4:2:0  */
    case V4L2_PIX_FMT_NV21:    /* 12  Y/CrCb 4:2:0  */
    case V4L2_PIX_FMT_YYUV:    /* 16  YUV 4:2:2     */
    case V4L2_PIX_FMT_HI240:   /*  8  8-bit color   */
      return YUV_ODD_BASE_RANK;

    case V4L2_PIX_FMT_YVU410:  /* YVU9,  9 bits per pixel */
      return YUV_BASE_RANK + 3;
    case V4L2_PIX_FMT_YUV410:  /* YUV9,  9 bits per pixel */
      return YUV_BASE_RANK + 2;
    case V4L2_PIX_FMT_YUV420:  /* I420, 12 bits per pixel */
      return YUV_BASE_RANK + 7;
    case V4L2_PIX_FMT_YUYV:    /* YUY2, 16 bits per pixel */
      return YUV_BASE_RANK + 10;
    case V4L2_PIX_FMT_YVU420:  /* YV12, 12 bits per pixel */
      return YUV_BASE_RANK + 6;
    case V4L2_PIX_FMT_UYVY:    /* UYVY, 16 bits per pixel */
      return YUV_BASE_RANK + 9;
    case V4L2_PIX_FMT_Y41P:    /* Y41P, 12 bits per pixel */
      return YUV_BASE_RANK + 5;
    case V4L2_PIX_FMT_YUV411P: /* Y41B, 12 bits per pixel */
      return YUV_BASE_RANK + 4;
    case V4L2_PIX_FMT_YUV422P: /* Y42B, 16 bits per pixel */
      return YUV_BASE_RANK + 8;

    case V4L2_PIX_FMT_DV:
      return DV_BASE_RANK;

    case V4L2_PIX_FMT_MPEG:    /* MPEG          */
    case V4L2_PIX_FMT_WNVA:    /* Winnov hw compres */
      return 0;

    case V4L2_PIX_FMT_SBGGR8:
      return BAYER_BASE_RANK;

    default:
      break;
  }

  return 0;
}

static gint
gst_v4l2src_format_cmp_func (gconstpointer a, gconstpointer b)
{
  guint32 pf1 = ((struct v4l2_fmtdesc *) a)->pixelformat;
  guint32 pf2 = ((struct v4l2_fmtdesc *) b)->pixelformat;

  if (pf1 == pf2)
    return 0;

  return gst_v4l2src_format_get_rank (pf2) - gst_v4l2src_format_get_rank (pf1);
}

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
    format = g_new0 (struct v4l2_fmtdesc, 1);

    format->index = n;
    format->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl (v4l2src->v4l2object->video_fd, VIDIOC_ENUM_FMT, format) < 0) {
      if (errno == EINVAL) {
        g_free (format);
        break;                  /* end of enumeration */
      } else {
        goto failed;
      }
    }

    GST_LOG_OBJECT (v4l2src, "index:       %u", format->index);
    GST_LOG_OBJECT (v4l2src, "type:        %d", format->type);
    GST_LOG_OBJECT (v4l2src, "flags:       %08x", format->flags);
    GST_LOG_OBJECT (v4l2src, "description: '%s'", format->description);
    GST_LOG_OBJECT (v4l2src, "pixelformat: %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (format->pixelformat));

    /* sort formats according to our preference;  we do this, because caps
     * are probed in the order the formats are in the list, and the order of
     * formats in the final probed caps matters for things like fixation */
    v4l2src->formats = g_slist_insert_sorted (v4l2src->formats, format,
        (GCompareFunc) gst_v4l2src_format_cmp_func);
  }

  GST_DEBUG_OBJECT (v4l2src, "got %d format(s)", n);

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
  GValue rates = { 0, };

  ret = gst_caps_new_empty ();

  memset (&ival, 0, sizeof (struct v4l2_frmivalenum));
  ival.index = 0;
  ival.pixel_format = pixelformat;
  ival.width = width;
  ival.height = height;

  GST_LOG_OBJECT (v4l2src, "get frame interval for %ux%u, %" GST_FOURCC_FORMAT,
      width, height, GST_FOURCC_ARGS (pixelformat));

  /* keep in mind that v4l2 gives us frame intervals (durations); we invert the
   * fraction to get framerate */
  if (ioctl (fd, VIDIOC_ENUM_FRAMEINTERVALS, &ival) < 0)
    goto enum_frameintervals_failed;

  if (ival.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
    GValue rate = { 0, };

    g_value_init (&rates, GST_TYPE_LIST);
    g_value_init (&rate, GST_TYPE_FRACTION);

    do {
      num = ival.discrete.numerator;
      denom = ival.discrete.denominator;

      if (num > G_MAXINT || denom > G_MAXINT) {
        /* let us hope we don't get here... */
        num >>= 1;
        denom >>= 1;
      }

      GST_LOG_OBJECT (v4l2src, "adding discrete framerate: %d/%d", denom, num);

      /* swap to get the framerate */
      gst_value_set_fraction (&rate, denom, num);
      gst_value_list_append_value (&rates, &rate);

      ival.index++;
    } while (ioctl (fd, VIDIOC_ENUM_FRAMEINTERVALS, &ival) >= 0);
  } else if (ival.type == V4L2_FRMIVAL_TYPE_STEPWISE) {
    GValue min = { 0, };
    GValue step = { 0, };
    GValue max = { 0, };
    gboolean added = FALSE;
    guint32 minnum, mindenom;
    guint32 maxnum, maxdenom;

    g_value_init (&rates, GST_TYPE_LIST);

    g_value_init (&min, GST_TYPE_FRACTION);
    g_value_init (&step, GST_TYPE_FRACTION);
    g_value_init (&max, GST_TYPE_FRACTION);

    /* get the min */
    minnum = ival.stepwise.min.numerator;
    mindenom = ival.stepwise.min.denominator;
    if (minnum > G_MAXINT || mindenom > G_MAXINT) {
      minnum >>= 1;
      mindenom >>= 1;
    }
    GST_LOG_OBJECT (v4l2src, "stepwise min frame interval: %d/%d", minnum,
        mindenom);
    gst_value_set_fraction (&min, minnum, mindenom);

    /* get the max */
    maxnum = ival.stepwise.max.numerator;
    maxdenom = ival.stepwise.max.denominator;
    if (maxnum > G_MAXINT || maxdenom > G_MAXINT) {
      maxnum >>= 1;
      maxdenom >>= 1;
    }

    GST_LOG_OBJECT (v4l2src, "stepwise max frame interval: %d/%d", maxnum,
        maxdenom);
    gst_value_set_fraction (&max, maxnum, maxdenom);

    /* get the step */
    num = ival.stepwise.step.numerator;
    denom = ival.stepwise.step.denominator;
    if (num > G_MAXINT || denom > G_MAXINT) {
      num >>= 1;
      denom >>= 1;
    }

    if (num == 0 || denom == 0) {
      /* in this case we have a wrong fraction or no step, set the step to max
       * so that we only add the min value in the loop below */
      num = maxnum;
      denom = maxdenom;
    }

    /* since we only have gst_value_fraction_subtract and not add, negate the
     * numerator */
    GST_LOG_OBJECT (v4l2src, "stepwise step frame interval: %d/%d", num, denom);
    gst_value_set_fraction (&step, -num, denom);

    while (gst_value_compare (&min, &max) <= 0) {
      GValue rate = { 0, };

      num = gst_value_get_fraction_numerator (&min);
      denom = gst_value_get_fraction_denominator (&min);
      GST_LOG_OBJECT (v4l2src, "adding stepwise framerate: %d/%d", denom, num);

      /* invert to get the framerate */
      g_value_init (&rate, GST_TYPE_FRACTION);
      gst_value_set_fraction (&rate, denom, num);
      gst_value_list_append_value (&rates, &rate);
      added = TRUE;

      /* we're actually adding because step was negated above. This is because
       * there is no _add function... */
      if (!gst_value_fraction_subtract (&min, &min, &step)) {
        GST_WARNING_OBJECT (v4l2src, "could not step fraction!");
        break;
      }
    }
    if (!added) {
      /* no range was added, make a default range */
      GST_WARNING_OBJECT (v4l2src, "no range added, setting 0/1 to 100/1");
      g_value_unset (&rates);
      g_value_init (&rates, GST_TYPE_FRACTION_RANGE);
      gst_value_set_fraction_range_full (&rates, 0, 1, 100, 1);
    }
  } else if (ival.type == V4L2_FRMIVAL_TYPE_CONTINUOUS) {
    guint32 maxnum, maxdenom;

    g_value_init (&rates, GST_TYPE_FRACTION_RANGE);

    num = ival.stepwise.min.numerator;
    denom = ival.stepwise.min.denominator;
    if (num > G_MAXINT || denom > G_MAXINT) {
      num >>= 1;
      denom >>= 1;
    }

    maxnum = ival.stepwise.max.numerator;
    maxdenom = ival.stepwise.max.denominator;
    if (maxnum > G_MAXINT || maxdenom > G_MAXINT) {
      maxnum >>= 1;
      maxdenom >>= 1;
    }

    GST_LOG_OBJECT (v4l2src, "continuous frame interval %d/%d to %d/%d",
        maxdenom, maxnum, denom, num);

    gst_value_set_fraction_range_full (&rates, maxdenom, maxnum, denom, num);
  } else {
    goto unknown_type;
  }

  s = gst_structure_copy (template);
  gst_structure_set (s, "width", G_TYPE_INT, (gint) width,
      "height", G_TYPE_INT, (gint) height, NULL);
  gst_structure_set_value (s, "framerate", &rates);
  g_value_unset (&rates);

  return s;

  /* ERRORS */
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
  GstCaps *ret;
  GstStructure *tmp;

#ifdef VIDIOC_ENUM_FRAMESIZES
  gint fd = v4l2src->v4l2object->video_fd;
  struct v4l2_frmsizeenum size;
  GList *results = NULL;
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
        results = g_list_prepend (results, tmp);

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

  /* we use an intermediary list to store the results of the probing because
   * we probe from lowest resolution to highest resolution, but want the caps
   * to contain the results in reverse order starting with the highest
   * resolution, as order in caps matters for things like fixation.  However,
   * there's no gst_caps_prepend_structure(), so we use the list as helper to
   * reverse the order */
  while (results != NULL) {
    gst_caps_append_structure (ret, GST_STRUCTURE (results->data));
    results = g_list_delete_link (results, results);
  }

  if (gst_caps_is_empty (ret))
    goto enum_framesizes_no_results;

  return ret;

  /* ERRORS */
enum_framesizes_failed:
  {
    /* I don't see how this is actually an error */
    GST_DEBUG_OBJECT (v4l2src,
        "Failed to enumerate frame sizes for pixelformat %" GST_FOURCC_FORMAT
        " (%s)", GST_FOURCC_ARGS (pixelformat), g_strerror (errno));
    goto default_frame_sizes;
  }
enum_framesizes_no_results:
  {
    /* it's possible that VIDIOC_ENUM_FRAMESIZES is defined but the driver in
     * question doesn't actually support it yet */
    GST_DEBUG_OBJECT (v4l2src, "No results for pixelformat %" GST_FOURCC_FORMAT
        " enumerating frame sizes, trying fallback",
        GST_FOURCC_ARGS (pixelformat));
    goto default_frame_sizes;
  }
unknown_type:
  {
    GST_WARNING_OBJECT (v4l2src,
        "Unknown frame sizeenum type for pixelformat %" GST_FOURCC_FORMAT
        ": %u", GST_FOURCC_ARGS (pixelformat), size.type);
    goto default_frame_sizes;
  }
default_frame_sizes:
#endif /* defined VIDIOC_ENUM_FRAMESIZES */
  {
    gint min_w, max_w, min_h, max_h;

    /* This code is for Linux < 2.6.19 */
    min_w = min_h = 1;
    max_w = max_h = GST_V4L2_MAX_SIZE;
    if (!gst_v4l2src_get_nearest_size (v4l2src, pixelformat, &min_w, &min_h)) {
      GST_WARNING_OBJECT (v4l2src,
          "Could not probe minimum capture size for pixelformat %"
          GST_FOURCC_FORMAT, GST_FOURCC_ARGS (pixelformat));
    }
    if (!gst_v4l2src_get_nearest_size (v4l2src, pixelformat, &max_w, &max_h)) {
      GST_WARNING_OBJECT (v4l2src,
          "Could not probe maximum capture size for pixelformat %"
          GST_FOURCC_FORMAT, GST_FOURCC_ARGS (pixelformat));
    }

    ret = gst_caps_new_empty ();
    tmp = gst_structure_copy (template);
    gst_structure_set (tmp,
        "width", GST_TYPE_INT_RANGE, min_w, max_w,
        "height", GST_TYPE_INT_RANGE, min_h, max_h,
        "framerate", GST_TYPE_FRACTION_RANGE, (gint) 0, (gint) 1, (gint) 100,
        (gint) 1, NULL);
    gst_caps_append_structure (ret, tmp);
  }
  return ret;
}

/******************************************************
 * gst_v4l2src_grab_frame ():
 *   grab a frame for capturing
 * return value: The captured frame number or -1 on error.
 ******************************************************/
GstFlowReturn
gst_v4l2src_grab_frame (GstV4l2Src * v4l2src, GstBuffer ** buf)
{
#define NUM_TRIALS 50
  struct v4l2_buffer buffer;
  gint32 trials = NUM_TRIALS;
  GstBuffer *pool_buffer;
  gboolean need_copy;
  gint index;

  memset (&buffer, 0x00, sizeof (buffer));
  buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buffer.memory = V4L2_MEMORY_MMAP;

  while (ioctl (v4l2src->v4l2object->video_fd, VIDIOC_DQBUF, &buffer) < 0) {

    GST_WARNING_OBJECT (v4l2src,
        "problem grabbing frame %d (ix=%d), trials=%d, pool-ct=%d, buf.flags=%d",
        buffer.sequence, buffer.index, trials,
        GST_MINI_OBJECT_REFCOUNT (v4l2src->pool), buffer.flags);

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
          /* FIXME: this is not a good idea, as drivers usualy return the buffer
           * with index-number set to 0, thus the re-enque will fail unless it
           * was incidentialy 0.
           * We could try to re-enque all buffers without handling the ioctl
           * return.
           */
          /*
             if (ioctl (v4l2src->v4l2object->video_fd, VIDIOC_QBUF, &buffer) < 0) {
             goto qbuf_failed;
             }
           */
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

  g_mutex_lock (v4l2src->pool->lock);

  index = buffer.index;

  /* get our GstBuffer with that index from the pool, if the buffer was
   * outstanding we have a serious problem. */
  pool_buffer = GST_BUFFER (v4l2src->pool->buffers[index]);

  if (pool_buffer == NULL)
    goto no_buffer;

  GST_LOG_OBJECT (v4l2src, "grabbed buffer %p at index %d", pool_buffer, index);

  /* we have the  buffer now, mark the spot in the pool empty */
  v4l2src->pool->buffers[index] = NULL;
  v4l2src->pool->num_live_buffers++;
  /* if we are handing out the last buffer in the pool, we need to make a
   * copy and bring the buffer back in the pool. */
  need_copy = v4l2src->always_copy
      || (v4l2src->pool->num_live_buffers == v4l2src->pool->buffer_count);

  g_mutex_unlock (v4l2src->pool->lock);

  /* this can change at every frame, esp. with jpeg */
  GST_BUFFER_SIZE (pool_buffer) = buffer.bytesused;

  GST_BUFFER_OFFSET (pool_buffer) = v4l2src->offset++;
  GST_BUFFER_OFFSET_END (pool_buffer) = v4l2src->offset;

  /* timestamps, LOCK to get clock and base time. */
  {
    GstClock *clock;
    GstClockTime timestamp;

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
    GST_BUFFER_TIMESTAMP (pool_buffer) = timestamp;
  }

  if (G_UNLIKELY (need_copy)) {
    *buf = gst_buffer_copy (pool_buffer);
    GST_BUFFER_FLAG_UNSET (*buf, GST_BUFFER_FLAG_READONLY);
    /* this will requeue */
    gst_buffer_unref (pool_buffer);
  } else {
    *buf = pool_buffer;
  }

  GST_LOG_OBJECT (v4l2src, "grabbed frame %d (ix=%d), flags %08x, pool-ct=%d",
      buffer.sequence, buffer.index, buffer.flags,
      v4l2src->pool->num_live_buffers);

  return GST_FLOW_OK;

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
    return GST_FLOW_ERROR;
  }
enomem:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, FAILED,
        (_("Failed trying to get video frames from device '%s'. Not enough memory."), v4l2src->v4l2object->videodev), (_("insufficient memory to enqueue a user pointer buffer. device %s."), v4l2src->v4l2object->videodev));
    return GST_FLOW_ERROR;
  }
too_many_trials:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, FAILED,
        (_("Failed trying to get video frames from device '%s'."),
            v4l2src->v4l2object->videodev),
        (_("Failed after %d tries. device %s. system error: %s"),
            NUM_TRIALS, v4l2src->v4l2object->videodev, g_strerror (errno)));
    return GST_FLOW_ERROR;
  }
no_buffer:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, FAILED,
        (_("Failed trying to get video frames from device '%s'."),
            v4l2src->v4l2object->videodev),
        (_("No free buffers found in the pool at index %d."), index));
    g_mutex_unlock (v4l2src->pool->lock);
    return GST_FLOW_ERROR;
  }
/*
qbuf_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, WRITE,
        (_("Could not exchange data with device '%s'."),
            v4l2src->v4l2object->videodev),
        ("Error queueing buffer on device %s. system error: %s",
            v4l2src->v4l2object->videodev, g_strerror (errno)));
    return GST_FLOW_ERROR;
  }
*/
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
  if (ioctl (fd, VIDIOC_G_PARM, &stream) < 0) {
    GST_ELEMENT_WARNING (v4l2src, RESOURCE, SETTINGS,
        (_("Could not get parameters on device '%s'"),
            v4l2src->v4l2object->videodev), GST_ERROR_SYSTEM);
  } else {                      /* seems no point in SET, if can't get GET */
    /* Note: V4L2 gives us the frame interval, we need the frame rate */
    stream.parm.capture.timeperframe.numerator = fps_d;
    stream.parm.capture.timeperframe.denominator = fps_n;

    if (ioctl (fd, VIDIOC_S_PARM, &stream) < 0) {
      GST_ELEMENT_WARNING (v4l2src, RESOURCE, SETTINGS,
          (_("Could not set parameters on device '%s'"),
              v4l2src->v4l2object->videodev), GST_ERROR_SYSTEM);

      /* FIXME: better test for fraction equality */
      if (stream.parm.capture.timeperframe.numerator != fps_d
          || stream.parm.capture.timeperframe.denominator != fps_n)
        goto invalid_framerate;
    }

    v4l2src->fps_d = fps_d;
    v4l2src->fps_n = fps_n;
  }

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
gst_v4l2src_capture_init (GstV4l2Src * v4l2src, GstCaps * caps)
{
  gint fd = v4l2src->v4l2object->video_fd;
  struct v4l2_requestbuffers breq;

  memset (&breq, 0, sizeof (struct v4l2_requestbuffers));

  GST_DEBUG_OBJECT (v4l2src, "initializing the capture system");

  GST_V4L2_CHECK_OPEN (v4l2src->v4l2object);
  GST_V4L2_CHECK_NOT_ACTIVE (v4l2src->v4l2object);

  if (v4l2src->v4l2object->vcap.capabilities & V4L2_CAP_STREAMING) {

    GST_DEBUG_OBJECT (v4l2src, "STREAMING, requesting %d MMAP CAPTURE buffers",
        v4l2src->num_buffers);

    breq.count = v4l2src->num_buffers;
    breq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    breq.memory = V4L2_MEMORY_MMAP;

    if (ioctl (fd, VIDIOC_REQBUFS, &breq) < 0)
      goto reqbufs_failed;

    GST_LOG_OBJECT (v4l2src, " count:  %u", breq.count);
    GST_LOG_OBJECT (v4l2src, " type:   %d", breq.type);
    GST_LOG_OBJECT (v4l2src, " memory: %d", breq.memory);

    if (breq.count < GST_V4L2_MIN_BUFFERS)
      goto no_buffers;

    if (v4l2src->num_buffers != breq.count) {
      GST_WARNING_OBJECT (v4l2src, "using %u buffers instead", breq.count);
      v4l2src->num_buffers = breq.count;
      g_object_notify (G_OBJECT (v4l2src), "queue-size");
    }

    /* Map the buffers */
    GST_LOG_OBJECT (v4l2src, "initiating buffer pool");

    if (!(v4l2src->pool = gst_v4l2_buffer_pool_new (v4l2src, fd,
                v4l2src->num_buffers, caps)))
      goto buffer_pool_new_failed;

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
buffer_pool_new_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, READ,
        (_("Could not map buffers from device '%s'"),
            v4l2src->v4l2object->videodev),
        ("Failed to create buffer pool: %s", g_strerror (errno)));
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
  gint fd = v4l2src->v4l2object->video_fd;

  GST_DEBUG_OBJECT (v4l2src, "starting the capturing");
  //GST_V4L2_CHECK_OPEN (v4l2src->v4l2object);
  GST_V4L2_CHECK_ACTIVE (v4l2src->v4l2object);

  v4l2src->quit = FALSE;

  if (v4l2src->use_mmap) {
    if (!gst_v4l2_buffer_pool_activate (v4l2src->pool, v4l2src))
      goto pool_activate_failed;

    if (ioctl (fd, VIDIOC_STREAMON, &type) < 0)
      goto streamon_failed;
  }

  v4l2src->is_capturing = TRUE;

  return TRUE;

  /* ERRORS */
pool_activate_failed:
  {
    /* already errored */
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
    gst_v4l2_buffer_pool_destroy (v4l2src->pool);
    v4l2src->pool = NULL;
  }

  GST_V4L2_SET_INACTIVE (v4l2src->v4l2object);

  return TRUE;
}

/*
 */
static gboolean
gst_v4l2src_get_nearest_size (GstV4l2Src * v4l2src, guint32 pixelformat,
    gint * width, gint * height)
{
  struct v4l2_format fmt;
  int fd;

  g_return_val_if_fail (width != NULL, FALSE);
  g_return_val_if_fail (height != NULL, FALSE);

  GST_LOG_OBJECT (v4l2src,
      "getting nearest size to %dx%d with format %" GST_FOURCC_FORMAT,
      *width, *height, GST_FOURCC_ARGS (pixelformat));

  fd = v4l2src->v4l2object->video_fd;

  /* get size delimiters */
  memset (&fmt, 0, sizeof (fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = *width;
  fmt.fmt.pix.height = *height;
  fmt.fmt.pix.pixelformat = pixelformat;
  fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

  if (ioctl (fd, VIDIOC_TRY_FMT, &fmt) < 0) {
    /* The driver might not implement TRY_FMT, in which case we will try
       S_FMT to probe */
    if (errno != ENOTTY)
      return FALSE;

    /* Only try S_FMT if we're not actively capturing yet, which we shouldn't
       be, because we're still probing */
    if (GST_V4L2_IS_ACTIVE (v4l2src->v4l2object))
      return FALSE;

    GST_LOG_OBJECT (v4l2src,
        "Failed to probe size limit with VIDIOC_TRY_FMT, trying VIDIOC_S_FMT");

    fmt.fmt.pix.width = *width;
    fmt.fmt.pix.height = *height;

    if (ioctl (fd, VIDIOC_S_FMT, &fmt) < 0)
      return FALSE;
  }

  GST_LOG_OBJECT (v4l2src,
      "got nearest size %dx%d", fmt.fmt.pix.width, fmt.fmt.pix.height);

  *width = fmt.fmt.pix.width;
  *height = fmt.fmt.pix.height;

  return TRUE;
}
