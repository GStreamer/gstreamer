/* G-Streamer BT8x8/V4L frame grabber plugin
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

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "v4lsrc_calls.h"
#include <sys/time.h>

/* number of buffers to be queued *at least* before syncing */
#define MIN_BUFFERS_QUEUED 2

/* On some systems MAP_FAILED seems to be missing */
#ifndef MAP_FAILED
#define MAP_FAILED ( (caddr_t) -1 )
#endif

#define DEBUG(format, args...) \
	GST_DEBUG_ELEMENT(GST_CAT_PLUGIN_INFO, \
		GST_ELEMENT(v4lsrc), \
		"V4LSRC: " format, ##args)

/* palette names */
static const char *palette_name[] = {
  "",                          /* 0 */
  "grayscale",                 /* VIDEO_PALETTE_GREY */
  "Hi-420",                    /* VIDEO_PALETTE_HI420 */
  "16-bit RGB (RGB-565)",      /* VIDEO_PALETTE_RB565 */
  "24-bit RGB",                /* VIDEO_PALETTE_RGB24 */
  "32-bit RGB",                /* VIDEO_PALETTE_RGB32 */
  "15-bit RGB (RGB-555)",      /* VIDEO_PALETTE_RGB555 */
  "YUV-4:2:2 (packed)",        /* VIDEO_PALETTE_YUV422 */
  "YUYV",                      /* VIDEO_PALETTE_YUYV */
  "UYVY",                      /* VIDEO_PALETTE_UYVY */
  "YUV-4:2:0 (packed)",        /* VIDEO_PALETTE_YUV420 */
  "YUV-4:1:1 (packed)",        /* VIDEO_PALETTE_YUV411 */
  "Raw",                       /* VIDEO_PALETTE_RAW */
  "YUV-4:2:2 (planar)",        /* VIDEO_PALETTE_YUV422P */
  "YUV-4:1:1 (planar)",        /* VIDEO_PALETTE_YUV411P */
  "YUV-4:2:0 (planar)",        /* VIDEO_PALETTE_YUV420P */
  "YUV-4:1:0 (planar)"         /* VIDEO_PALETTE_YUV410P */
};

#define FRAME_QUEUE_READY -2
#define FRAME_ERROR       -1
#define FRAME_DONE         0
/* FRAME_QUEUED is used in frame_queued array */
#define FRAME_QUEUED       1
/* FRAME_SOFTSYNCED is used in is_ready_soft array */
#define FRAME_SYNCED   1

/******************************************************
 * gst_v4lsrc_queue_frame():
 *   queue a frame for capturing
 * return value: TRUE on success, FALSE on error
 ******************************************************/

static gboolean
gst_v4lsrc_queue_frame (GstV4lSrc *v4lsrc,
                        gint      num)
{
  DEBUG("queueing frame %d", num);

  v4lsrc->mmap.frame = num;

  if (v4lsrc->frame_queued[num] == FRAME_ERROR) {
    return TRUE;
  }

  if (ioctl(GST_V4LELEMENT(v4lsrc)->video_fd, VIDIOCMCAPTURE, &(v4lsrc->mmap)) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error queueing a buffer (%d): %s",
      num, g_strerror(errno));
    return FALSE;
  }

  v4lsrc->frame_queued[num] = FRAME_QUEUED;

  return TRUE;
}
/******************************************************
 * gst_v4lsrc_hard_sync_frame(GstV4lSrc *v4lsrc,gint num)
 *   sync a frame and set the timestamp correctly
 *****************************************************/
static gboolean 
gst_v4lsrc_hard_sync_frame(GstV4lSrc *v4lsrc,gint num) {

    DEBUG("Hardware syncing frame %d",num);

    while (ioctl(GST_V4LELEMENT(v4lsrc)->video_fd, VIDIOCSYNC, &num) < 0) {
      /* if the sync() got interrupted, we can retry */
      if (errno != EINTR) {
        v4lsrc->isready_soft_sync[num] = FRAME_ERROR; 
        v4lsrc->frame_queued[num] = FRAME_ERROR;
        gst_element_error(GST_ELEMENT(v4lsrc),
           "Error syncing a buffer (%d): %s",
            num, g_strerror(errno));
        return FALSE;
      }
      DEBUG("Sync got interrupted");
    }

    v4lsrc->frame_queued[num] = FRAME_DONE;

    g_mutex_lock(v4lsrc->mutex_soft_sync);

    if (v4lsrc->clock) {
      v4lsrc->timestamp_soft_sync[num] = gst_clock_get_time(v4lsrc->clock);
    } else {
      GTimeVal time;
      g_get_current_time(&time);
      v4lsrc->timestamp_soft_sync[num] = GST_TIMEVAL_TO_TIME(time);
    }
    v4lsrc->isready_soft_sync[num] = FRAME_SYNCED;
    g_cond_broadcast(v4lsrc->cond_soft_sync[num]);

    g_mutex_unlock(v4lsrc->mutex_soft_sync);

    return TRUE;
}

/******************************************************
 * gst_v4lsrc_soft_sync_cleanup()
 *   cleans up the v4lsrc structure after an error or 
 *   exit request and exits the thread
 ******************************************************/
static void
gst_v4lsrc_soft_sync_cleanup(GstV4lSrc *v4lsrc) {
  int n;

  DEBUG("Software sync thread exiting");
  /* sync all queued buffers */
  for (n=0;n < v4lsrc->mbuf.frames; n++) { 
    if (v4lsrc->frame_queued[n] == FRAME_QUEUED) {
      gst_v4lsrc_hard_sync_frame(v4lsrc,n);
      g_mutex_lock(v4lsrc->mutex_soft_sync);
      v4lsrc->isready_soft_sync[n] = FRAME_ERROR;
      g_cond_broadcast(v4lsrc->cond_soft_sync[n]);
      g_mutex_unlock(v4lsrc->mutex_soft_sync);
    }
  }

  g_thread_exit(NULL);
}

/******************************************************
 * gst_v4lsrc_soft_sync_thread()
 *   syncs on frames and signals the main thread
 * purpose: actually get the correct frame timestamps
 ******************************************************/

static void *
gst_v4lsrc_soft_sync_thread (void *arg)
{
  GstV4lSrc *v4lsrc = GST_V4LSRC(arg);
  gint frame = 0;
  gint qframe = 0;
  gint nqueued = 0;

  DEBUG("starting software sync thread");

  for (;;) {
    /* queue as many frames as we can */
    while (v4lsrc->frame_queued[qframe] == FRAME_QUEUE_READY) { 
      if (v4lsrc->quit || !gst_v4lsrc_queue_frame(v4lsrc,qframe)) {
        gst_v4lsrc_soft_sync_cleanup(v4lsrc);
      }
      qframe = (qframe + 1) % v4lsrc->mbuf.frames;
      nqueued++;
    }

    if (nqueued < MIN_BUFFERS_QUEUED) {
      /* not enough frames queued, wait for one to get ready and queue as much
       * as we can again */
      DEBUG("!enough buffers, waiting for frame %d",qframe);

      g_mutex_lock(v4lsrc->mutex_queued_frames);

      if (v4lsrc->quit) {
        g_mutex_unlock(v4lsrc->mutex_queued_frames);   
        gst_v4lsrc_soft_sync_cleanup(v4lsrc);
      }

      if (!(v4lsrc->frame_queued[qframe] == FRAME_QUEUE_READY)) {
        g_cond_wait(v4lsrc->cond_queued_frames,v4lsrc->mutex_queued_frames);
      }
      g_mutex_unlock(v4lsrc->mutex_queued_frames);

    } else {
      if (!gst_v4lsrc_hard_sync_frame(v4lsrc,frame))
        gst_v4lsrc_soft_sync_cleanup(v4lsrc);
      frame = (frame + 1) % v4lsrc->mbuf.frames;
      nqueued--;
    }
  }
  g_assert_not_reached();
}


/******************************************************
 * gst_v4lsrc_sync_frame():
 *   sync on a frame for capturing
 * return value: TRUE on success, FALSE on error
 ******************************************************/

static gboolean
gst_v4lsrc_sync_next_frame (GstV4lSrc *v4lsrc,
                            gint      *num)
{
  *num = v4lsrc->sync_frame;

  DEBUG("syncing on next frame (%d)", *num);

  /* "software sync()" on the frame */
  g_mutex_lock(v4lsrc->mutex_soft_sync);
  while (v4lsrc->isready_soft_sync[*num] == FRAME_DONE)
  {
    DEBUG("Waiting for frame %d to be synced on", *num);
    g_cond_wait(v4lsrc->cond_soft_sync[*num], v4lsrc->mutex_soft_sync);
  }
  g_mutex_unlock(v4lsrc->mutex_soft_sync);

  if (v4lsrc->isready_soft_sync[*num] != FRAME_SYNCED)
    return FALSE;
  v4lsrc->isready_soft_sync[*num] = FRAME_DONE;

  v4lsrc->sync_frame = (v4lsrc->sync_frame + 1)%v4lsrc->mbuf.frames;

  return TRUE;
}


/******************************************************
 * gst_v4lsrc_set_capture():
 *   set capture parameters, palette = VIDEO_PALETTE_*
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lsrc_set_capture (GstV4lSrc *v4lsrc,
                        gint      width,
                        gint      height,
                        gint      palette)
{
  DEBUG("capture properties set to width = %d, height = %d, palette = %d",
    width, height, palette);

  /*GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));*/
  /*GST_V4L_CHECK_NOT_ACTIVE(GST_V4LELEMENT(v4lsrc));*/

  v4lsrc->mmap.width = width;
  v4lsrc->mmap.height = height;
  v4lsrc->mmap.format = palette;

  return TRUE;
}


/******************************************************
 * gst_v4lsrc_capture_init():
 *   initialize the capture system
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lsrc_capture_init (GstV4lSrc *v4lsrc)
{
  int n;

  DEBUG("initting capture subsystem");
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));
  GST_V4L_CHECK_NOT_ACTIVE(GST_V4LELEMENT(v4lsrc));

  /* request buffer info */
  if (ioctl(GST_V4LELEMENT(v4lsrc)->video_fd, VIDIOCGMBUF, &(v4lsrc->mbuf)) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error getting buffer information: %s",
      g_strerror(errno));
    return FALSE;
  }

  if (v4lsrc->mbuf.frames < MIN_BUFFERS_QUEUED)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Too little buffers. We got %d, we want at least %d",
      v4lsrc->mbuf.frames, MIN_BUFFERS_QUEUED);
    return FALSE;
  }

  gst_info("Got %d buffers (\'%s\') of size %d KB\n",
    v4lsrc->mbuf.frames, palette_name[v4lsrc->mmap.format],
    v4lsrc->mbuf.size/(v4lsrc->mbuf.frames*1024));

  /* keep track of queued buffers */
  v4lsrc->frame_queued = (gint8 *) malloc(sizeof(gint8) * v4lsrc->mbuf.frames);
  if (!v4lsrc->frame_queued)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error creating buffer tracker: %s",
      g_strerror(errno));
    return FALSE;
  }

  /* init the GThread stuff */
  v4lsrc->mutex_soft_sync = g_mutex_new();
  v4lsrc->isready_soft_sync = (gint8 *) malloc(sizeof(gint8) * v4lsrc->mbuf.frames);
  if (!v4lsrc->isready_soft_sync)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error creating software-sync buffer tracker: %s",
      g_strerror(errno));
    return FALSE;
  }
  v4lsrc->timestamp_soft_sync = (GstClockTime *)
    malloc(sizeof(GstClockTime) * v4lsrc->mbuf.frames);
  if (!v4lsrc->timestamp_soft_sync)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error creating software-sync timestamp tracker: %s",
      g_strerror(errno));
    return FALSE;
  }
  v4lsrc->cond_soft_sync = (GCond **) malloc( sizeof(GCond *) * v4lsrc->mbuf.frames);
  if (!v4lsrc->cond_soft_sync)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error creating software-sync condition tracker: %s",
      g_strerror(errno));
    return FALSE;
  }
  for (n=0;n<v4lsrc->mbuf.frames;n++)
    v4lsrc->cond_soft_sync[n] = g_cond_new();
  v4lsrc->use_num_times = (gint *) malloc(sizeof(gint) * v4lsrc->mbuf.frames);
  if (!v4lsrc->use_num_times)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error creating sync-use-time tracker: %s",
      g_strerror(errno));
    return FALSE;
  }

  v4lsrc->mutex_queued_frames = g_mutex_new();
  v4lsrc->cond_queued_frames = g_cond_new();

  /* Map the buffers */
  GST_V4LELEMENT(v4lsrc)->buffer = mmap(0, v4lsrc->mbuf.size, 
    PROT_READ|PROT_WRITE, MAP_SHARED, GST_V4LELEMENT(v4lsrc)->video_fd, 0);
  if (GST_V4LELEMENT(v4lsrc)->buffer == MAP_FAILED)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error mapping video buffers: %s",
      g_strerror(errno));
    GST_V4LELEMENT(v4lsrc)->buffer = NULL;
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4lsrc_capture_start():
 *   start streaming capture
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lsrc_capture_start (GstV4lSrc *v4lsrc)
{
  GError *error = NULL;
  int n;

  DEBUG("starting capture");
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lsrc));

  v4lsrc->quit = FALSE;
  /* set all buffers ready to queue , this starts streaming capture */
  for (n=0;n<v4lsrc->mbuf.frames;n++) {
    v4lsrc->isready_soft_sync[n] = FRAME_DONE;
    v4lsrc->frame_queued[n] = FRAME_QUEUE_READY;
  }

  v4lsrc->sync_frame = 0;
  /* start the sync() thread (correct timestamps) */
  v4lsrc->thread_soft_sync = g_thread_create(gst_v4lsrc_soft_sync_thread,
    (void *) v4lsrc, TRUE, &error);
  if (!v4lsrc->thread_soft_sync)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Failed to create software sync thread: %s",error->message);
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4lsrc_grab_frame():
 *   capture one frame during streaming capture
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lsrc_grab_frame (GstV4lSrc *v4lsrc, gint *num)
{
  DEBUG("grabbing frame");
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lsrc));

  /* syncing on the buffer grabs it */
  if (!gst_v4lsrc_sync_next_frame(v4lsrc, num))
    return FALSE;

  return TRUE;
}


/******************************************************
 * gst_v4lsrc_get_buffer():
 *   get the address of the just-capture buffer
 * return value: the buffer's address or NULL
 ******************************************************/

guint8 *
gst_v4lsrc_get_buffer (GstV4lSrc *v4lsrc, gint  num)
{
  /*DEBUG("gst_v4lsrc_get_buffer(), num = %d", num);*/

  if (!GST_V4L_IS_ACTIVE(GST_V4LELEMENT(v4lsrc)) ||
      !GST_V4L_IS_OPEN(GST_V4LELEMENT(v4lsrc)))
    return NULL;

  if (num < 0 || num >= v4lsrc->mbuf.frames)
    return NULL;

  return GST_V4LELEMENT(v4lsrc)->buffer+v4lsrc->mbuf.offsets[num];
}


/******************************************************
 * gst_v4lsrc_requeue_frame():
 *   re-queue a frame after we're done with the buffer
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lsrc_requeue_frame (GstV4lSrc *v4lsrc, gint  num)
{
  DEBUG("requeueing frame %d", num);
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lsrc));

  /* mark frame as 'ready to requeue' */
  g_mutex_lock(v4lsrc->mutex_queued_frames);

  v4lsrc->frame_queued[num] = FRAME_QUEUE_READY;
  g_cond_broadcast(v4lsrc->cond_queued_frames);

  g_mutex_unlock(v4lsrc->mutex_queued_frames);

  return TRUE;
}


/******************************************************
 * gst_v4lsrc_capture_stop():
 *   stop streaming capture
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lsrc_capture_stop (GstV4lSrc *v4lsrc)
{
  DEBUG("stopping capture");
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lsrc));

  /* we actually need to sync on all queued buffers but
   * not on the non-queued ones */
  g_mutex_lock(v4lsrc->mutex_queued_frames);
  v4lsrc->quit = TRUE;
  g_cond_broadcast(v4lsrc->cond_queued_frames);
  g_mutex_unlock(v4lsrc->mutex_queued_frames);

  g_thread_join(v4lsrc->thread_soft_sync);

  return TRUE;
}


/******************************************************
 * gst_v4lsrc_capture_deinit():
 *   deinitialize the capture system
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lsrc_capture_deinit (GstV4lSrc *v4lsrc)
{
  int n;

  DEBUG("quitting capture subsystem");
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lsrc));

  /* free buffer tracker */
  g_mutex_free(v4lsrc->mutex_queued_frames);
  for (n=0;n<v4lsrc->mbuf.frames;n++)
    g_cond_free(v4lsrc->cond_soft_sync[n]);
  free(v4lsrc->frame_queued);
  free(v4lsrc->cond_soft_sync);
  free(v4lsrc->isready_soft_sync);
  free(v4lsrc->timestamp_soft_sync);
  free(v4lsrc->use_num_times);

  /* unmap the buffer */
  munmap(GST_V4LELEMENT(v4lsrc)->buffer, v4lsrc->mbuf.size);
  GST_V4LELEMENT(v4lsrc)->buffer = NULL;

  return TRUE;
}


/******************************************************
 * gst_v4lsrc_try_palette():
 *   try out a palette on the device
 *   This has to be done before initializing the
 *   actual capture system, to make sure we don't
 *   mess up anything. So we need to mini-mmap()
 *   a buffer here, queue and sync on one buffer,
 *   and unmap it.
 *   This is ugly, yes, I know - but it's a major
 *   design flaw of v4l1 that you don't know in
 *   advance which formats will be supported...
 *   This is better than "just assuming that it'll
 *   work"...
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lsrc_try_palette (GstV4lSrc *v4lsrc,
                        gint       palette)
{
  /* so, we need a buffer and some more stuff */
  int frame = 0;
  guint8 *buffer;
  struct video_mbuf vmbuf;
  struct video_mmap vmmap;

  DEBUG("gonna try out palette format %d (%s)",
    palette, palette_name[palette]);
  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));
  GST_V4L_CHECK_NOT_ACTIVE(GST_V4LELEMENT(v4lsrc));

  /* let's start by requesting a buffer and mmap()'ing it */
  if (ioctl(GST_V4LELEMENT(v4lsrc)->video_fd, VIDIOCGMBUF, &vmbuf) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error getting buffer information: %s",
      g_strerror(errno));
    return FALSE;
  }
  /* Map the buffers */
  buffer = mmap(0, vmbuf.size, PROT_READ|PROT_WRITE,
                MAP_SHARED, GST_V4LELEMENT(v4lsrc)->video_fd, 0);
  if (buffer == MAP_FAILED)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error mapping our try-out buffer: %s",
      g_strerror(errno));
    return FALSE;
  }

  /* now that we have a buffer, let's try out our format */
  vmmap.width = GST_V4LELEMENT(v4lsrc)->vcap.minwidth;
  vmmap.height = GST_V4LELEMENT(v4lsrc)->vcap.minheight;
  vmmap.format = palette;
  vmmap.frame = frame;
  if (ioctl(GST_V4LELEMENT(v4lsrc)->video_fd, VIDIOCMCAPTURE, &vmmap) < 0)
  {
    if (errno != EINVAL) /* our format failed! */
      gst_element_error(GST_ELEMENT(v4lsrc),
        "Error queueing our try-out buffer: %s",
        g_strerror(errno));
    munmap(buffer, vmbuf.size);
    return FALSE;
  }

  if (ioctl(GST_V4LELEMENT(v4lsrc)->video_fd, VIDIOCSYNC, &frame) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error syncing on a buffer (%d): %s",
      frame, g_strerror(errno));
    munmap(buffer, vmbuf.size);
    return FALSE;
  }

  munmap(buffer, vmbuf.size);

  /* if we got here, it worked! woohoo, the format is supported! */
  return TRUE;
}

