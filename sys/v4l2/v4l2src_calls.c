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
#include "gstv4l2bufferpool.h"

#include "gst/gst-i18n-plugin.h"

#define GST_CAT_DEFAULT v4l2src_debug
GST_DEBUG_CATEGORY_EXTERN (GST_CAT_PERFORMANCE);


/******************************************************
 * gst_v4l2src_grab_frame ():
 *   grab a frame for capturing
 * return value: GST_FLOW_OK, GST_FLOW_WRONG_STATE or GST_FLOW_ERROR
 ******************************************************/
GstFlowReturn
gst_v4l2src_grab_frame (GstV4l2Src * v4l2src, GstBuffer ** buf)
{
#define NUM_TRIALS 50
  GstV4l2Object *v4l2object;
  GstV4l2BufferPool *pool;
  gint32 trials = NUM_TRIALS;
  GstBuffer *pool_buffer;
  gboolean need_copy;
  gint ret;

  v4l2object = v4l2src->v4l2object;
  pool = v4l2object->pool;
  if (!pool)
    goto no_buffer_pool;

  GST_DEBUG_OBJECT (v4l2src, "grab frame");

  for (;;) {
    if (v4l2object->can_poll_device) {
      ret = gst_poll_wait (v4l2object->poll, GST_CLOCK_TIME_NONE);
      if (G_UNLIKELY (ret < 0)) {
        if (errno == EBUSY)
          goto stopped;
        if (errno == ENXIO) {
          GST_DEBUG_OBJECT (v4l2src,
              "v4l2 device doesn't support polling. Disabling");
          v4l2object->can_poll_device = FALSE;
        } else {
          if (errno != EAGAIN && errno != EINTR)
            goto select_error;
        }
      }
    }

    pool_buffer = gst_v4l2_buffer_pool_dqbuf (pool);
    if (pool_buffer)
      break;

    GST_WARNING_OBJECT (v4l2src, "trials=%d", trials);

    /* if the sync() got interrupted, we can retry */
    switch (errno) {
      case EINVAL:
      case ENOMEM:
        /* fatal */
        return GST_FLOW_ERROR;

      case EAGAIN:
      case EIO:
      case EINTR:
      default:
        /* try again, until too many trials */
        break;
    }

    /* check nr. of attempts to capture */
    if (--trials == -1) {
      goto too_many_trials;
    }
  }

  /* if we are handing out the last buffer in the pool, we need to make a
   * copy and bring the buffer back in the pool. */
  need_copy = v4l2src->always_copy
      || !gst_v4l2_buffer_pool_available_buffers (pool);

  if (G_UNLIKELY (need_copy)) {
    if (!v4l2src->always_copy) {
      GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, v4l2src,
          "running out of buffers, making a copy to reuse current one");
    }
    *buf = gst_buffer_copy (pool_buffer);
    /* this will requeue */
    gst_buffer_unref (pool_buffer);
  } else {
    *buf = pool_buffer;
  }
  /* we set the buffer metadata in gst_v4l2src_create() */

  return GST_FLOW_OK;

  /* ERRORS */
no_buffer_pool:
  {
    GST_DEBUG ("no buffer pool");
    return GST_FLOW_WRONG_STATE;
  }
select_error:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, READ, (NULL),
        ("select error %d: %s (%d)", ret, g_strerror (errno), errno));
    return GST_FLOW_ERROR;
  }
stopped:
  {
    GST_DEBUG ("stop called");
    return GST_FLOW_WRONG_STATE;
  }
too_many_trials:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, FAILED,
        (_("Failed trying to get video frames from device '%s'."),
            v4l2object->videodev),
        (_("Failed after %d tries. device %s. system error: %s"),
            NUM_TRIALS, v4l2object->videodev, g_strerror (errno)));
    return GST_FLOW_ERROR;
  }
}
