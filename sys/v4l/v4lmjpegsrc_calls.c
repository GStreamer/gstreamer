/* G-Streamer hardware MJPEG video source plugin
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "v4lmjpegsrc_calls.h"

/* On some systems MAP_FAILED seems to be missing */
#ifndef MAP_FAILED
#define MAP_FAILED ( (caddr_t) -1 )
#endif

#define MIN_BUFFERS_QUEUED 2

#define DEBUG(format, args...) \
	GST_DEBUG_OBJECT (\
		GST_ELEMENT(v4lmjpegsrc), \
		"V4LMJPEGSRC: " format, ##args)

enum {
  QUEUE_STATE_ERROR = -1,
  QUEUE_STATE_READY_FOR_QUEUE,
  QUEUE_STATE_QUEUED,
  QUEUE_STATE_SYNCED,
};

/******************************************************
 * gst_v4lmjpegsrc_queue_frame():
 *   queue a frame for capturing
 * return value: TRUE on success, FALSE on error
 ******************************************************/

static gboolean
gst_v4lmjpegsrc_queue_frame (GstV4lMjpegSrc *v4lmjpegsrc,
                             gint           num)
{
  DEBUG("queueing frame %d", num);

  if (v4lmjpegsrc->frame_queue_state[num] != QUEUE_STATE_READY_FOR_QUEUE) {
    return FALSE;
  }

  if (ioctl(GST_V4LELEMENT(v4lmjpegsrc)->video_fd, MJPIOC_QBUF_CAPT, &num) < 0)
  {
    GST_ELEMENT_ERROR (v4lmjpegsrc, RESOURCE, READ, (NULL),
      ("Error queueing a buffer (%d): %s",
      num, g_strerror(errno)));
    return FALSE;
  }

  v4lmjpegsrc->frame_queue_state[num] = QUEUE_STATE_QUEUED;
  v4lmjpegsrc->num_queued++;

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsrc_sync_next_frame():
 *   sync on the next frame for capturing
 * return value: TRUE on success, FALSE on error
 ******************************************************/

static gboolean
gst_v4lmjpegsrc_sync_next_frame (GstV4lMjpegSrc *v4lmjpegsrc,
                                 gint           *num)
{
  DEBUG("syncing on next frame");

  if (v4lmjpegsrc->num_queued <= 0) {
    return FALSE;
  }

  while (ioctl(GST_V4LELEMENT(v4lmjpegsrc)->video_fd,
               MJPIOC_SYNC, &(v4lmjpegsrc->bsync)) < 0) {
    if (errno != EINTR) {
      GST_ELEMENT_ERROR (v4lmjpegsrc, RESOURCE, SYNC, NULL, GST_ERROR_SYSTEM);
      return FALSE;
    }
    DEBUG("Sync got interrupted");
  }

  *num = v4lmjpegsrc->bsync.frame;

  v4lmjpegsrc->frame_queue_state[*num] = QUEUE_STATE_SYNCED;
  v4lmjpegsrc->num_queued--;

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsrc_set_buffer():
 *   set buffer parameters (size/count)
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsrc_set_buffer (GstV4lMjpegSrc *v4lmjpegsrc,
                            gint           numbufs,
                            gint           bufsize)
{
  DEBUG("setting buffer info to numbufs = %d, bufsize = %d KB",
    numbufs, bufsize);
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lmjpegsrc));
  GST_V4L_CHECK_NOT_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc));

  v4lmjpegsrc->breq.size = bufsize * 1024;
  v4lmjpegsrc->breq.count = numbufs;

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsrc_set_capture():
 *   set capture parameters (simple)
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsrc_set_capture (GstV4lMjpegSrc *v4lmjpegsrc,
                             gint           decimation,
                             gint           quality)
{
  int norm, input, mw;
  struct mjpeg_params bparm;

  DEBUG("setting decimation = %d, quality = %d",
    decimation, quality);
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lmjpegsrc));
  GST_V4L_CHECK_NOT_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc));

  gst_v4l_get_chan_norm(GST_V4LELEMENT(v4lmjpegsrc), &input, &norm);

  /* Query params for capture */
  if (ioctl(GST_V4LELEMENT(v4lmjpegsrc)->video_fd, MJPIOC_G_PARAMS, &bparm) < 0)
  {
    GST_ELEMENT_ERROR (v4lmjpegsrc, RESOURCE, SETTINGS, NULL, GST_ERROR_SYSTEM);
    return FALSE;
  }

  bparm.decimation = decimation;
  bparm.quality = quality;
  bparm.norm = norm;
  bparm.input = input;
  bparm.APP_len = 0; /* no JPEG markers - TODO: this is definately not right for decimation==1 */

  mw = GST_V4LELEMENT(v4lmjpegsrc)->vcap.maxwidth;
  if (mw != 768 && mw != 640)
  {
    if (decimation == 1)
      mw = 720;
    else
      mw = 704;
  }
  v4lmjpegsrc->end_width = mw / decimation;
  v4lmjpegsrc->end_height = (norm==VIDEO_MODE_NTSC?480:576) / decimation;

  /* TODO: interlacing */

  /* Set params for capture */
  if (ioctl(GST_V4LELEMENT(v4lmjpegsrc)->video_fd, MJPIOC_S_PARAMS, &bparm) < 0)
  {
    GST_ELEMENT_ERROR (v4lmjpegsrc, RESOURCE, SETTINGS, NULL, GST_ERROR_SYSTEM);
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsrc_set_capture_m():
 *   set capture parameters (advanced)
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean gst_v4lmjpegsrc_set_capture_m (GstV4lMjpegSrc *v4lmjpegsrc,
                                        gint           x_offset,
                                        gint           y_offset,
                                        gint           width,
                                        gint           height,
                                        gint           h_decimation,
                                        gint           v_decimation,
                                        gint           quality)
{
  gint norm, input;
  gint maxwidth;
  struct mjpeg_params bparm;

  DEBUG("setting x_offset = %d, y_offset = %d, "
    "width = %d, height = %d, h_decimation = %d, v_decimation = %d, quality = %d\n",
    x_offset, y_offset, width, height, h_decimation, v_decimation, quality);
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lmjpegsrc));
  GST_V4L_CHECK_NOT_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc));

  gst_v4l_get_chan_norm(GST_V4LELEMENT(v4lmjpegsrc), &input, &norm);

  if (GST_V4LELEMENT(v4lmjpegsrc)->vcap.maxwidth != 768 &&
    GST_V4LELEMENT(v4lmjpegsrc)->vcap.maxwidth != 640)
    maxwidth = 720;
  else
    maxwidth = GST_V4LELEMENT(v4lmjpegsrc)->vcap.maxwidth;

  /* Query params for capture */
  if (ioctl(GST_V4LELEMENT(v4lmjpegsrc)->video_fd, MJPIOC_G_PARAMS, &bparm) < 0)
  {
    GST_ELEMENT_ERROR (v4lmjpegsrc, RESOURCE, SETTINGS, NULL, GST_ERROR_SYSTEM);
    return FALSE;
  }

  bparm.decimation = 0;
  bparm.quality = quality;
  bparm.norm = norm;
  bparm.input = input;
  bparm.APP_len = 0; /* no JPEG markers - TODO: this is definately
                      * not right for decimation==1 */

  if (width <= 0)
  {
    if (x_offset < 0) x_offset = 0;
    width = (maxwidth==720&&h_decimation!=1)?704:maxwidth - 2*x_offset;
  }
  else
  {
    if (x_offset < 0)
      x_offset = (maxwidth - width)/2;
  }

  if (height <= 0)
  {
    if (y_offset < 0) y_offset = 0;
    height = (norm==VIDEO_MODE_NTSC)?480:576 - 2*y_offset;
  }
  else
  {
    if (y_offset < 0)
      y_offset = ((norm==VIDEO_MODE_NTSC)?480:576 - height)/2;
  }

  if (width + x_offset > maxwidth)
  {
    GST_ELEMENT_ERROR (v4lmjpegsrc, RESOURCE, TOO_LAZY, NULL,
      ("Image width+offset (%d) bigger than maximum (%d)",
      width + x_offset, maxwidth));
    return FALSE;
  }
  if ((width%(bparm.HorDcm*16))!=0) 
  {
    GST_ELEMENT_ERROR (v4lmjpegsrc, STREAM, FORMAT, NULL,
      ("Image width (%d) not multiple of %d (required for JPEG)",
      width, bparm.HorDcm*16));
    return FALSE;
  }
  if (height + y_offset > (norm==VIDEO_MODE_NTSC ? 480 : 576)) 
  {
    GST_ELEMENT_ERROR (v4lmjpegsrc, RESOURCE, TOO_LAZY, NULL,
      ("Image height+offset (%d) bigger than maximum (%d)",
      height + y_offset, (norm==VIDEO_MODE_NTSC ? 480 : 576)));
    return FALSE;
  }
  /* RJ: Image height must only be a multiple of 8, but geom_height
   * is double the field height
   */
  if ((height%(bparm.VerDcm*16))!=0) 
  {
    GST_ELEMENT_ERROR (v4lmjpegsrc, STREAM, FORMAT, NULL,
      ("Image height (%d) not multiple of %d (required for JPEG)",
      height, bparm.VerDcm*16));
    return FALSE;
  }

  bparm.img_x = x_offset;
  bparm.img_width = width;
  bparm.img_y = y_offset;
  bparm.img_height = height;
  bparm.HorDcm = h_decimation;
  bparm.VerDcm = (v_decimation==4) ? 2 : 1;
  bparm.TmpDcm = (v_decimation==1) ? 1 : 2;
  bparm.field_per_buff = (v_decimation==1) ? 2 : 1;

  v4lmjpegsrc->end_width = width / h_decimation;
  v4lmjpegsrc->end_width = height / v_decimation;

  /* TODO: interlacing */

  /* Set params for capture */
  if (ioctl(GST_V4LELEMENT(v4lmjpegsrc)->video_fd, MJPIOC_S_PARAMS, &bparm) < 0)
  {
    GST_ELEMENT_ERROR (v4lmjpegsrc, RESOURCE, SETTINGS, NULL, GST_ERROR_SYSTEM);
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsrc_capture_init():
 *   initialize the capture system
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsrc_capture_init (GstV4lMjpegSrc *v4lmjpegsrc)
{
  DEBUG("initting capture subsystem");
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lmjpegsrc));
  GST_V4L_CHECK_NOT_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc));

  /* Request buffers */
  if (ioctl(GST_V4LELEMENT(v4lmjpegsrc)->video_fd,
            MJPIOC_REQBUFS, &(v4lmjpegsrc->breq)) < 0)
  {
    GST_ELEMENT_ERROR (v4lmjpegsrc, RESOURCE, READ, NULL, GST_ERROR_SYSTEM);
    return FALSE;
  }

  if (v4lmjpegsrc->breq.count < MIN_BUFFERS_QUEUED)
  {
    GST_ELEMENT_ERROR (v4lmjpegsrc, RESOURCE, READ, NULL,
      ("Too little buffers. We got %ld, we want at least %d",
      v4lmjpegsrc->breq.count, MIN_BUFFERS_QUEUED));
    return FALSE;
  }

  gst_info("Got %ld buffers of size %ld KB\n",
    v4lmjpegsrc->breq.count, v4lmjpegsrc->breq.size/1024);

  /* keep track of queued buffers */
  v4lmjpegsrc->frame_queue_state = (gint8 *)
    g_malloc(sizeof(gint8) * v4lmjpegsrc->breq.count);

  /* track how often to use each frame */
  v4lmjpegsrc->use_num_times = (gint *)
    g_malloc(sizeof(gint) * v4lmjpegsrc->breq.count);

  /* lock for the frame_state */
  v4lmjpegsrc->mutex_queue_state = g_mutex_new();
  v4lmjpegsrc->cond_queue_state = g_cond_new();

  /* Map the buffers */
  GST_V4LELEMENT(v4lmjpegsrc)->buffer = mmap(0,
    v4lmjpegsrc->breq.count * v4lmjpegsrc->breq.size, 
    PROT_READ|PROT_WRITE, MAP_SHARED, GST_V4LELEMENT(v4lmjpegsrc)->video_fd, 0);
  if (GST_V4LELEMENT(v4lmjpegsrc)->buffer == MAP_FAILED)
  {
    GST_ELEMENT_ERROR (v4lmjpegsrc, RESOURCE, TOO_LAZY, NULL,
      ("Error mapping video buffers: %s", g_strerror (errno)));
    GST_V4LELEMENT(v4lmjpegsrc)->buffer = NULL;
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsrc_capture_start():
 *   start streaming capture
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsrc_capture_start (GstV4lMjpegSrc *v4lmjpegsrc)
{
  int n;

  DEBUG("starting capture");
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lmjpegsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc));

  g_mutex_lock(v4lmjpegsrc->mutex_queue_state);

  v4lmjpegsrc->quit = FALSE;
  v4lmjpegsrc->num_queued = 0;
  v4lmjpegsrc->queue_frame = 0;

  /* set all buffers ready to queue , this starts streaming capture */
  for (n=0;n<v4lmjpegsrc->breq.count;n++) {
    v4lmjpegsrc->frame_queue_state[n] = QUEUE_STATE_READY_FOR_QUEUE;
    if (!gst_v4lmjpegsrc_queue_frame(v4lmjpegsrc, n)) {
      g_mutex_unlock(v4lmjpegsrc->mutex_queue_state);
      gst_v4lmjpegsrc_capture_stop(v4lmjpegsrc);
      return FALSE;
    }
  }

  g_mutex_unlock(v4lmjpegsrc->mutex_queue_state);

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsrc_grab_frame():
 *   grab one frame during streaming capture
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsrc_grab_frame (GstV4lMjpegSrc *v4lmjpegsrc,
                            gint           *num,
                            gint           *size)
{
  DEBUG("grabbing frame");
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lmjpegsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc));

  g_mutex_lock(v4lmjpegsrc->mutex_queue_state);

  /* do we have enough frames? */
  while (v4lmjpegsrc->num_queued < MIN_BUFFERS_QUEUED ||
         v4lmjpegsrc->frame_queue_state[v4lmjpegsrc->queue_frame] ==
           QUEUE_STATE_READY_FOR_QUEUE) {
    while (v4lmjpegsrc->frame_queue_state[v4lmjpegsrc->queue_frame] !=
             QUEUE_STATE_READY_FOR_QUEUE &&
           !v4lmjpegsrc->quit) {
      GST_DEBUG (
                "Waiting for frames to become available (%d < %d)",
                v4lmjpegsrc->num_queued, MIN_BUFFERS_QUEUED);
      g_cond_wait(v4lmjpegsrc->cond_queue_state,
                  v4lmjpegsrc->mutex_queue_state);
    }
    if (v4lmjpegsrc->quit) {
      g_mutex_unlock(v4lmjpegsrc->mutex_queue_state);
      return TRUE; /* it won't get through anyway */
    }
    if (!gst_v4lmjpegsrc_queue_frame(v4lmjpegsrc, v4lmjpegsrc->queue_frame)) {
      g_mutex_unlock(v4lmjpegsrc->mutex_queue_state);
      return FALSE;
    }
    v4lmjpegsrc->queue_frame = (v4lmjpegsrc->queue_frame + 1) % v4lmjpegsrc->breq.count;
  }

  /* syncing on the buffer grabs it */
  if (!gst_v4lmjpegsrc_sync_next_frame(v4lmjpegsrc, num)) {
    return FALSE;
  }

  *size = v4lmjpegsrc->bsync.length;

  g_mutex_unlock(v4lmjpegsrc->mutex_queue_state);

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsrc_get_buffer():
 *   get the memory address of a single buffer
 * return value: TRUE on success, FALSE on error
 ******************************************************/

guint8 *
gst_v4lmjpegsrc_get_buffer (GstV4lMjpegSrc *v4lmjpegsrc,
                            gint           num)
{
  /*DEBUG("gst_v4lmjpegsrc_get_buffer(), num = %d", num);*/

  if (!GST_V4L_IS_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc)) ||
      !GST_V4L_IS_OPEN(GST_V4LELEMENT(v4lmjpegsrc)))
    return NULL;

  if (num < 0 || num >= v4lmjpegsrc->breq.count)
    return NULL;

  return GST_V4LELEMENT(v4lmjpegsrc)->buffer+(v4lmjpegsrc->breq.size*num);
}


/******************************************************
 * gst_v4lmjpegsrc_requeue_frame():
 *   requeue a frame for capturing
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsrc_requeue_frame (GstV4lMjpegSrc *v4lmjpegsrc,
                               gint           num)
{
  DEBUG("requeueing frame %d", num);
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lmjpegsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc));

  /* mark frame as 'ready to requeue' */
  g_mutex_lock(v4lmjpegsrc->mutex_queue_state);

  if (v4lmjpegsrc->frame_queue_state[num] != QUEUE_STATE_SYNCED) {
    GST_ELEMENT_ERROR (v4lmjpegsrc, RESOURCE, TOO_LAZY, NULL,
                      ("Invalid state %d (expected %d), can't requeue",
                      v4lmjpegsrc->frame_queue_state[num],
                      QUEUE_STATE_SYNCED));
    return FALSE;
  }

  v4lmjpegsrc->frame_queue_state[num] = QUEUE_STATE_READY_FOR_QUEUE;

  /* let an optional wait know */
  g_cond_broadcast(v4lmjpegsrc->cond_queue_state);

  g_mutex_unlock(v4lmjpegsrc->mutex_queue_state);

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsrc_capture_stop():
 *   stop streaming capture
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsrc_capture_stop (GstV4lMjpegSrc *v4lmjpegsrc)
{
  int n;

  DEBUG("stopping capture");
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lmjpegsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc));

  g_mutex_lock(v4lmjpegsrc->mutex_queue_state);

  /* make an optional pending wait stop */
  v4lmjpegsrc->quit = TRUE;
  g_cond_broadcast(v4lmjpegsrc->cond_queue_state);
                                                                                
  /* sync on remaining frames */
  while (v4lmjpegsrc->num_queued > 0) {
    gst_v4lmjpegsrc_sync_next_frame(v4lmjpegsrc, &n);
  }

  g_mutex_unlock(v4lmjpegsrc->mutex_queue_state);

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsrc_capture_deinit():
 *   deinitialize the capture system
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsrc_capture_deinit (GstV4lMjpegSrc *v4lmjpegsrc)
{
  DEBUG("quitting capture subsystem");
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lmjpegsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc));

  /* unmap the buffer */
  munmap(GST_V4LELEMENT(v4lmjpegsrc)->buffer, v4lmjpegsrc->breq.size * v4lmjpegsrc->breq.count);
  GST_V4LELEMENT(v4lmjpegsrc)->buffer = NULL;

  /* free buffer tracker */
  g_mutex_free(v4lmjpegsrc->mutex_queue_state);
  g_cond_free(v4lmjpegsrc->cond_queue_state);
  g_free(v4lmjpegsrc->frame_queue_state);
  g_free(v4lmjpegsrc->use_num_times);

  return TRUE;
}
