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

//#define DEBUG

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


/* palette names */
char *palette_name[] = {
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


/******************************************************
 * gst_v4lsrc_queue_frame():
 *   queue a frame for capturing
 * return value: TRUE on success, FALSE on error
 ******************************************************/

static gboolean
gst_v4lsrc_queue_frame (GstV4lSrc *v4lsrc,
                        gint      num)
{
#ifdef DEBUG
  fprintf(stderr, "V4LSRC: gst_v4lsrc_queue_frame(), num = %d\n",
    num);
#endif

  v4lsrc->mmap.frame = num;

  if (v4lsrc->frame_queued[num] < 0)
  {
    v4lsrc->frame_queued[num] = 0;
    return TRUE;
  }

  if (ioctl(GST_V4LELEMENT(v4lsrc)->video_fd, VIDIOCMCAPTURE, &(v4lsrc->mmap)) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error queueing a buffer (%d): %s",
      num, sys_errlist[errno]);
    return FALSE;
  }

  v4lsrc->frame_queued[num] = 1;

  pthread_mutex_lock(&(v4lsrc->mutex_queued_frames));
  v4lsrc->num_queued_frames++;
  pthread_cond_broadcast(&(v4lsrc->cond_queued_frames));
  pthread_mutex_unlock(&(v4lsrc->mutex_queued_frames));

  return TRUE;
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

#ifdef DEBUG
  fprintf(stderr, "gst_v4lsrc_soft_sync_thread()\n");
#endif

  /* Allow easy shutting down by other processes... */
  pthread_setcancelstate( PTHREAD_CANCEL_ENABLE, NULL );
  pthread_setcanceltype( PTHREAD_CANCEL_ASYNCHRONOUS, NULL );

  while (1)
  {
    /* are there queued frames left? */
    pthread_mutex_lock(&(v4lsrc->mutex_queued_frames));
    if (v4lsrc->num_queued_frames < MIN_BUFFERS_QUEUED)
    {
      if (v4lsrc->frame_queued[frame] < 0)
        break;
#ifdef DEBUG
      fprintf(stderr, "Waiting for new frames to be queued (%d < %d)\n",
        v4lsrc->num_queued_frames, MIN_BUFFERS_QUEUED);
#endif
      pthread_cond_wait(&(v4lsrc->cond_queued_frames),
        &(v4lsrc->mutex_queued_frames));
    }
    pthread_mutex_unlock(&(v4lsrc->mutex_queued_frames));

    if (!v4lsrc->num_queued_frames)
    {
#ifdef DEBUG
      fprintf(stderr, "Got signal to exit...\n");
#endif
      goto end;
    }

    /* sync on the frame */
#ifdef DEBUG
    fprintf(stderr, "Sync\'ing on frame %d\n", frame);
#endif
retry:
    if (ioctl(GST_V4LELEMENT(v4lsrc)->video_fd, VIDIOCSYNC, &frame) < 0)
    {
      /* if the sync() got interrupted, we can retry */
      if (errno == EINTR)
        goto retry;
      gst_element_error(GST_ELEMENT(v4lsrc),
        "Error syncing on a buffer (%d): %s",
        frame, sys_errlist[errno]);
      pthread_mutex_lock(&(v4lsrc->mutex_soft_sync));
      v4lsrc->isready_soft_sync[frame] = -1;
      pthread_cond_broadcast(&(v4lsrc->cond_soft_sync[frame]));
      pthread_mutex_unlock(&(v4lsrc->mutex_soft_sync));
      goto end;
    }
    else
    {
      pthread_mutex_lock(&(v4lsrc->mutex_soft_sync));
      gettimeofday(&(v4lsrc->timestamp_soft_sync[frame]), NULL);
      v4lsrc->isready_soft_sync[frame] = 1;
      pthread_cond_broadcast(&(v4lsrc->cond_soft_sync[frame]));
      pthread_mutex_unlock(&(v4lsrc->mutex_soft_sync));
    }

    pthread_mutex_lock(&(v4lsrc->mutex_queued_frames));
    v4lsrc->num_queued_frames--;
    v4lsrc->frame_queued[frame] = 0;
    pthread_mutex_unlock(&(v4lsrc->mutex_queued_frames));

    frame = (frame+1)%v4lsrc->mbuf.frames;
  }

end:
#ifdef DEBUG
  fprintf(stderr, "Software sync thread got signalled to exit\n");
#endif
  pthread_exit(NULL);
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
#ifdef DEBUG
  fprintf(stderr, "V4LSRC: gst_v4lsrc_sync_frame()\n");
#endif

  *num = v4lsrc->sync_frame = (v4lsrc->sync_frame + 1)%v4lsrc->mbuf.frames;

  /* "software sync()" on the frame */
  pthread_mutex_lock(&(v4lsrc->mutex_soft_sync));
  if (v4lsrc->isready_soft_sync[*num] == 0)
  {
#ifdef DEBUG
    fprintf(stderr, "Waiting for frame %d to be synced on\n",
      *num);
#endif
    pthread_cond_wait(&(v4lsrc->cond_soft_sync[*num]),
      &(v4lsrc->mutex_soft_sync));
  }

  if (v4lsrc->isready_soft_sync[*num] < 0)
    return FALSE;
  v4lsrc->isready_soft_sync[*num] = 0;
  pthread_mutex_unlock(&(v4lsrc->mutex_soft_sync));

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
#ifdef DBUG
  fprintf(stderr, "V4LSRC: gst_v4lsrc_set_capture(), width = %d, height = %d, palette = %d\n",
    width, height, palette);
#endif

  /*GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));*/
  GST_V4L_CHECK_NOT_ACTIVE(GST_V4LELEMENT(v4lsrc));

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

#ifdef DEBUG
  fprintf(stderr, "V4LSRC: gst_v4lsrc_capture_init()\n");
#endif

  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));
  GST_V4L_CHECK_NOT_ACTIVE(GST_V4LELEMENT(v4lsrc));

  /* request buffer info */
  if (ioctl(GST_V4LELEMENT(v4lsrc)->video_fd, VIDIOCGMBUF, &(v4lsrc->mbuf)) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error getting buffer information: %s",
      sys_errlist[errno]);
    return FALSE;
  }

  if (v4lsrc->mbuf.frames < MIN_BUFFERS_QUEUED)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Too little buffers. We got %d, we want at least %d",
      v4lsrc->mbuf.frames, MIN_BUFFERS_QUEUED);
    return FALSE;
  }

  g_message("Got %d buffers (\'%s\') of size %d KB\n",
    v4lsrc->mbuf.frames, palette_name[v4lsrc->mmap.format],
    v4lsrc->mbuf.size/(v4lsrc->mbuf.frames*1024));

  /* keep trakc of queued buffers */
  v4lsrc->frame_queued = (gint8 *) malloc(sizeof(gint8) * v4lsrc->mbuf.frames);
  if (!v4lsrc->frame_queued)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error creating buffer tracker: %s",
      sys_errlist[errno]);
    return FALSE;
  }
  for (n=0;n<v4lsrc->mbuf.frames;n++)
    v4lsrc->frame_queued[n] = 0;

  /* init the pthread stuff */
  pthread_mutex_init(&(v4lsrc->mutex_soft_sync), NULL);
  v4lsrc->isready_soft_sync = (gint8 *) malloc(sizeof(gint8) * v4lsrc->mbuf.frames);
  if (!v4lsrc->isready_soft_sync)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error creating software-sync buffer tracker: %s",
      sys_errlist[errno]);
    return FALSE;
  }
  for (n=0;n<v4lsrc->mbuf.frames;n++)
    v4lsrc->isready_soft_sync[n] = 0;
  v4lsrc->timestamp_soft_sync = (struct timeval *)
    malloc(sizeof(struct timeval) * v4lsrc->mbuf.frames);
  if (!v4lsrc->timestamp_soft_sync)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error creating software-sync timestamp tracker: %s",
      sys_errlist[errno]);
    return FALSE;
  }
  v4lsrc->cond_soft_sync = (pthread_cond_t *)
    malloc(sizeof(pthread_cond_t) * v4lsrc->mbuf.frames);
  if (!v4lsrc->cond_soft_sync)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error creating software-sync condition tracker: %s",
      sys_errlist[errno]);
    return FALSE;
  }
  for (n=0;n<v4lsrc->mbuf.frames;n++)
    pthread_cond_init(&(v4lsrc->cond_soft_sync[n]), NULL);

  pthread_mutex_init(&(v4lsrc->mutex_queued_frames), NULL);
  pthread_cond_init(&(v4lsrc->cond_queued_frames), NULL);

  /* Map the buffers */
  GST_V4LELEMENT(v4lsrc)->buffer = mmap(0, v4lsrc->mbuf.size, 
    PROT_READ|PROT_WRITE, MAP_SHARED, GST_V4LELEMENT(v4lsrc)->video_fd, 0);
  if (GST_V4LELEMENT(v4lsrc)->buffer == MAP_FAILED)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error mapping video buffers: %s",
      sys_errlist[errno]);
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
  int n;

#ifdef DEBUG
  fprintf(stderr, "V4LSRC: gst_v4lsrc_capture_start()\n");
#endif

  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lsrc));

  v4lsrc->num_queued_frames = 0;

  /* queue all buffers, this starts streaming capture */
  for (n=0;n<v4lsrc->mbuf.frames;n++)
    if (!gst_v4lsrc_queue_frame(v4lsrc, n))
      return FALSE;

  v4lsrc->sync_frame = -1;

  /* start the sync() thread (correct timestamps) */
  if ( pthread_create( &(v4lsrc->thread_soft_sync), NULL,
    gst_v4lsrc_soft_sync_thread, (void *) v4lsrc ) )
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Failed to create software sync thread: %s",
      sys_errlist[errno]);
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
#ifdef DEBUG
  fprintf(stderr, "V4LSRC: gst_v4lsrc_grab_frame()\n");
#endif

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
#ifdef DEBUG
  fprintf(stderr, "V4LSRC: gst_v4lsrc_get_buffer(), num = %d\n",
    num);
#endif

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
#ifdef DEBUG
  fprintf(stderr, "V4LSRC: gst_v4lsrc_requeue_buffer(), num = %d\n",
    num);
#endif

  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lsrc));

  /* and let's queue the buffer */
  if (!gst_v4lsrc_queue_frame(v4lsrc, num))
    return FALSE;

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
  int n, num;

#ifdef DEBUG
  fprintf(stderr, "V4LSRC: gst_v4lsrc_capture_stop()\n");
#endif

  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lsrc));

  /* we actually need to sync on all queued buffers but not on the non-queued ones */
  for (n=0;n<v4lsrc->mbuf.frames;n++)
    v4lsrc->frame_queued[n] = -1;

  pthread_join(v4lsrc->thread_soft_sync, NULL);

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
#ifdef DEBUG
  fprintf(stderr, "V4LSRC: gst_v4lsrc_capture_deinit()\n");
#endif

  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lsrc));

  /* free buffer tracker */
  free(v4lsrc->frame_queued);
  free(v4lsrc->cond_soft_sync);
  free(v4lsrc->isready_soft_sync);
  free(v4lsrc->timestamp_soft_sync);

  /* unmap the buffer */
  munmap(GST_V4LELEMENT(v4lsrc)->buffer, v4lsrc->mbuf.size);
  GST_V4LELEMENT(v4lsrc)->buffer = NULL;

  return TRUE;
}
