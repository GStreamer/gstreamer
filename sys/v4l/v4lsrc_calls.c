/* G-Streamer BT8x8/V4L frame grabber plugin
 * Copyright (C) 2001 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

/* On some systems MAP_FAILED seems to be missing */
#ifndef MAP_FAILED
#define MAP_FAILED ( (caddr_t) -1 )
#endif


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

  if (ioctl(GST_V4LELEMENT(v4lsrc)->video_fd, VIDIOCMCAPTURE, &(v4lsrc->mmap)) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error queueing a buffer (%d): %s",
      num, sys_errlist[errno]);
    return FALSE;
  }

  v4lsrc->frame_queued[num] = TRUE;

  return TRUE;
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
  fprintf(stderr, "V4LSRC: gst_v4lsrc_sync_frame(), num = %d\n",
    num);
#endif

  *num = (v4lsrc->sync_frame + 1)%v4lsrc->mbuf.frames;

  if (ioctl(GST_V4LELEMENT(v4lsrc)->video_fd, VIDIOCSYNC, num) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error syncing on a buffer (%d): %s",
      *num, sys_errlist[errno]);
    return FALSE;
  }

  v4lsrc->frame_queued[*num] = FALSE;

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

  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lsrc));
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

  gst_element_info(GST_ELEMENT(v4lsrc),
    "Got %d buffers of size %d KB",
    v4lsrc->mbuf.frames, v4lsrc->mbuf.size/(v4lsrc->mbuf.frames*1024));

  /* keep trakc of queued buffers */
  v4lsrc->frame_queued = (gint *) malloc(sizeof(gint) * v4lsrc->mbuf.frames);
  if (!v4lsrc->frame_queued)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Error creating buffer tracker: %s",
      sys_errlist[errno]);
    return FALSE;
  }
  for (n=0;n<v4lsrc->mbuf.frames;n++)
    v4lsrc->frame_queued[n] = FALSE;

  /* Map the buffers */
  GST_V4LELEMENT(v4lsrc)->buffer = mmap(0, v4lsrc->mbuf.size, 
    PROT_READ, MAP_SHARED, GST_V4LELEMENT(v4lsrc)->video_fd, 0);
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

  /* queue all buffers, this starts streaming capture */
  for (n=0;n<v4lsrc->mbuf.frames;n++)
    if (!gst_v4lsrc_queue_frame(v4lsrc, n))
      return FALSE;

  v4lsrc->sync_frame = -1;

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
 * return value: TRUE on success, FALSE on error
 ******************************************************/

guint8 *
gst_v4lsrc_get_buffer (GstV4lSrc *v4lsrc, gint  num)
{
#ifdef DEBUG
  fprintf(stderr, "V4LSRC: gst_v4lsrc_get_buffer(), num = %d\n",
    num);
#endif

  if (!GST_V4L_IS_ACTIVE(GST_V4LELEMENT(v4lsrc)))
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
    while (v4lsrc->frame_queued[n])
      if (!gst_v4lsrc_sync_next_frame(v4lsrc, &num))
        return FALSE;

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

  /* unmap the buffer */
  munmap(GST_V4LELEMENT(v4lsrc)->buffer, v4lsrc->mbuf.size);
  GST_V4LELEMENT(v4lsrc)->buffer = NULL;

  return TRUE;
}
